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

/* xx_regex.c - a backtracking regular expression engine.
 *
 * Supported syntax: literals, '.', character classes with ranges and
 * negation, the \d \D \w \W \s \S \b \B escapes, greedy and lazy
 * quantifiers (* + ? {n,m}), grouping, non-capturing groups, alternation,
 * anchors, back references and look-ahead assertions. Flags: g, i, m.
 *
 * Matching uses an explicit continuation chain so that quantified groups
 * backtrack correctly without needing to rewrite the AST.
 */

#include "xx_regex_internal.h"
#include "xx_regex_search.h"
#include "xxfclib/rt/xx_rt.h"
#include <xxfclib/threads/xx_threads.h>

typedef enum {
    RE_CHAR = 0, /* single literal byte             */
    RE_ANY,      /* '.'                             */
    RE_CLASS,    /* character class                 */
    RE_GROUP,    /* group with alternatives         */
    RE_BACKREF,  /* \1 .. \9                        */
    RE_BOL,      /* ^                               */
    RE_EOL,      /* $                               */
    RE_WORDB,    /* \b                              */
    RE_NWORDB,   /* \B                              */
    RE_LOOKAHEAD /* (?=...) and (?!...)             */
} RENodeType;

typedef struct RENode RENode;
typedef struct REAlt REAlt;

struct RENode {
    RENodeType type;
    unsigned char nChar;
    unsigned char pClass[32]; /* 256 bit bitmap */
    int bNegateClass;
    int nGroupIndex; /* -1 when non-capturing */
    int nCaptureFirst, nCaptureLast; /* Captures inside this quantified body. */
    int bNegateLook;
    REAlt *pAlts;
    int nAltCount;
    int nBackref;

    int nMin;
    int nMax; /* -1 = unbounded */
    int bLazy;

    RENode *pNext;
};

struct REAlt {
    RENode *pFirst;
    size_t nMinBytes;
    size_t nCheckOffset;
    unsigned char pFirstBytes[32];
    unsigned char nCheckChar;
    int bCanEmpty;
    int bAnchored;
    int bCheckChar;
};

struct xx_regex {
    volatile long nRefs;
    char *pSource;
    char *pFlags;
    size_t nSourceSize, nFlagsSize;
    int bGlobal;
    int bIgnoreCase;
    int bMultiline;
    int nGroups;
    unsigned options;
    REAlt *pAlts;
    int nAltCount;
    int bAllAnchored;
    int nSingleFirstByte;
};

typedef struct {
    const char *p;
    const char *pEnd;
    xx_regex *pRegExp;
    char *pError;
    int bIgnoreCase;
    int nGroupCount; /* capturing groups counted before the parse */
    int nDepth;
    int bError;
    const xx_regex_allocator *pAllocator; /* Borrowed only while compiling. */
} REParser;

/* ------------------------------------------------------------------ util  */

/* The public core reports allocation failures without interpreter policy.
 * Adapters may preserve their own compatible-heap allocation policy. */
static const xx_regex_allocator regex_default_allocator = {
    xx_rt_malloc, xx_rt_calloc, xx_rt_realloc
};

static char *regex_strndup(const char *text, size_t size, const xx_regex_allocator *pAllocator)
{
    char *copy;
    if (size == SIZE_MAX) return NULL;
    copy = (char *)pAllocator->malloc_fn(size + 1);
    if (copy) {
        if (size) xx_rt_memcpy(copy, text, size);
        copy[size] = 0;
    }
    return copy;
}

static char *regex_strdup(const char *text, const xx_regex_allocator *pAllocator)
{
    return regex_strndup(text, xx_rt_strlen(text), pAllocator);
}

static void class_set(unsigned char *pClass, unsigned char nChar)
{
    pClass[nChar >> 3] |= (unsigned char)(1u << (nChar & 7));
}

static int class_get(const unsigned char *pClass, unsigned char nChar)
{
    return (pClass[nChar >> 3] & (1u << (nChar & 7))) ? 1 : 0;
}

/* API metadata encodes Latin-1 section characters in UTF-8. A class must
 * inspect the character, never a continuation byte inside that character. */
static unsigned char latin1_at(const char *pText, size_t nSize, size_t nPos, int *pnWidth)
{
    unsigned char first = (unsigned char)pText[nPos];
    *pnWidth = 1;
    if ((first == 0xC2 || first == 0xC3) && nPos + 1 < nSize) {
        unsigned char second = (unsigned char)pText[nPos + 1];
        if (second >= 0x80 && second <= 0xBF) {
            *pnWidth = 2;
            return (unsigned char)(((first & 3) << 6) | (second & 0x3F));
        }
    }
    return first;
}

static char re_lower(char nChar)
{
    if ((nChar >= 'A') && (nChar <= 'Z')) {
        return (char)(nChar - 'A' + 'a');
    }

    return nChar;
}

static int is_word_char(unsigned char nChar)
{
    return (((nChar >= 'a') && (nChar <= 'z')) || ((nChar >= 'A') && (nChar <= 'Z')) || ((nChar >= '0') && (nChar <= '9')) || (nChar == '_')) ? 1 : 0;
}

static void class_add_digit(unsigned char *pClass)
{
    int i = 0;

    for (i = '0'; i <= '9'; i++) {
        class_set(pClass, (unsigned char)i);
    }
}

static void class_add_word(unsigned char *pClass)
{
    int i = 0;

    for (i = 0; i < 256; i++) {
        if (is_word_char((unsigned char)i)) {
            class_set(pClass, (unsigned char)i);
        }
    }
}

static void class_add_space(unsigned char *pClass, int bStandalone)
{
    static const char *pSpaces = " \t\n\r\f\v";
    int i = 0;

    for (i = 0; pSpaces[i]; i++) {
        class_set(pClass, (unsigned char)pSpaces[i]);
    }
    /* QtScript includes NBSP for standalone \s, while its character-class
     * escape retains the ASCII set. Preserve both reference behaviors. */
    if (bStandalone) class_set(pClass, 0xA0);
}

static void class_invert(unsigned char *pClass)
{
    int i = 0;

    for (i = 0; i < 32; i++) {
        pClass[i] = (unsigned char)(~pClass[i]);
    }
}

/* ---------------------------------------------------------------- parser  */

/* The reference engine caps a {n,m} bound at 0xFFFFFFFF and turns a larger
 * one into an atom that can never match. RE_QUANT_MAX is above the length of
 * any subject we can be handed, so clamping a smaller bound to it is not
 * observable.                                                              */
#define RE_QUANT_LIMIT 0xFFFFFFFFu
#define RE_QUANT_MAX 0x40000000

/* Groups nest through parse_atom -> parse_alternatives -> parse_sequence, so
 * a pattern built from untrusted data needs a ceiling of its own.          */
#define RE_MAX_PARSE_DEPTH 1000

static REAlt *parse_alternatives(REParser *pParser, int *pnCount);

static void parse_error(REParser *pParser, const char *pMessage)
{
    pParser->bError = 1;
    if (pParser->pError == NULL) {
        pParser->pError = regex_strdup(pMessage, pParser->pAllocator);
    }
}

/* Counts the capturing groups up front: '\1' is only a back reference when
 * its number does not exceed the group count, and that count is not known
 * yet at the point where the escape is parsed.                             */
static int count_groups(const char *p, const char *pEnd)
{
    int nCount = 0;
    int bInClass = 0;

    while (p < pEnd) {
        if (*p == '\\') {
            p += 2;

            continue;
        }

        if (bInClass) {
            if (*p == ']') {
                bInClass = 0;
            }
        } else if (*p == '[') {
            bInClass = 1;
        } else if ((*p == '(') && (((p + 1) >= pEnd) || (p[1] != '?'))) {
            nCount++;
        }

        p++;
    }

    return nCount;
}

/* True when the parser sits on a quantifier. '{' only counts when the whole
 * '{n}', '{n,}' or '{n,m}' is there; anything else is a literal brace.     */
static int is_quantifier(REParser *pParser)
{
    const char *p = pParser->p;

    if (p >= pParser->pEnd) {
        return 0;
    }

    if ((*p == '*') || (*p == '+') || (*p == '?')) {
        return 1;
    }

    if (*p != '{') {
        return 0;
    }

    p++;

    if ((p >= pParser->pEnd) || (*p < '0') || (*p > '9')) {
        return 0;
    }

    while ((p < pParser->pEnd) && (*p >= '0') && (*p <= '9')) {
        p++;
    }

    if ((p < pParser->pEnd) && (*p == ',')) {
        p++;

        while ((p < pParser->pEnd) && (*p >= '0') && (*p <= '9')) {
            p++;
        }
    }

    return ((p < pParser->pEnd) && (*p == '}')) ? 1 : 0;
}

static RENode *node_alloc(REParser *pParser, RENodeType type)
{
    RENode *pNode = (RENode *)pParser->pAllocator->calloc_fn(1, sizeof(RENode));

    if (!pNode) { parse_error(pParser, "out of memory"); return NULL; }
    pNode->type = type;
    pNode->nGroupIndex = -1;
    pNode->nMin = 1;
    pNode->nMax = 1;

    return pNode;
}

static void free_alts(REAlt *pAlts, int nCount);

static void free_nodes(RENode *pNode)
{
    while (pNode) {
        RENode *pNext = pNode->pNext;

        if (pNode->pAlts) {
            free_alts(pNode->pAlts, pNode->nAltCount);
        }

        xx_rt_free(pNode);
        pNode = pNext;
    }
}

static void free_alts(REAlt *pAlts, int nCount)
{
    int i = 0;

    for (i = 0; i < nCount; i++) {
        free_nodes(pAlts[i].pFirst);
    }

    xx_rt_free(pAlts);
}

static void parse_escape_class(REParser *pParser, unsigned char *pClass, int *pbHandled, int bStandalone)
{
    char nChar = *pParser->p;

    *pbHandled = 1;

    switch (nChar) {
        case 'd':
            class_add_digit(pClass);
            pParser->p++;
            return;
        case 'D':
            class_add_digit(pClass);
            class_invert(pClass);
            pParser->p++;
            return;
        case 'w':
            class_add_word(pClass);
            pParser->p++;
            return;
        case 'W':
            class_add_word(pClass);
            class_invert(pClass);
            pParser->p++;
            return;
        case 's':
            class_add_space(pClass, bStandalone);
            pParser->p++;
            return;
        case 'S':
            class_add_space(pClass, bStandalone);
            class_invert(pClass);
            pParser->p++;
            return;
        default: *pbHandled = 0; return;
    }
}

static int hex_digit(char nChar)
{
    if ((nChar >= '0') && (nChar <= '9')) {
        return nChar - '0';
    }

    if ((nChar >= 'a') && (nChar <= 'f')) {
        return nChar - 'a' + 10;
    }

    if ((nChar >= 'A') && (nChar <= 'F')) {
        return nChar - 'A' + 10;
    }

    return -1;
}

static unsigned char parse_escape_char(REParser *pParser)
{
    char nChar = 0;

    if (pParser->p >= pParser->pEnd) {
        return '\\';
    }

    nChar = *pParser->p;
    pParser->p++;

    switch (nChar) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'f': return '\f';
        case 'v': return '\v';
        case '0': return '\0';
        case 'b': return '\b'; /* only meaningful inside a class */
        case 'x': {
            int a = (pParser->p < pParser->pEnd) ? hex_digit(pParser->p[0]) : -1;
            int b = ((pParser->p + 1) < pParser->pEnd) ? hex_digit(pParser->p[1]) : -1;

            if ((a >= 0) && (b >= 0)) {
                pParser->p += 2;

                return (unsigned char)((a << 4) | b);
            }

            return 'x';
        }
        case 'u': {
            int i = 0;
            unsigned int nCode = 0;

            for (i = 0; i < 4; i++) {
                int d = ((pParser->p + i) < pParser->pEnd) ? hex_digit(pParser->p[i]) : -1;

                if (d < 0) {
                    return 'u';
                }

                nCode = nCode * 16 + (unsigned int)d;
            }

            pParser->p += 4;

            return (unsigned char)(nCode & 0xFF);
        }
        default: return (unsigned char)nChar;
    }
}

static void class_set_ci(REParser *pParser, RENode *pNode, unsigned char nChar)
{
    class_set(pNode->pClass, nChar);

    if (pParser->bIgnoreCase) {
        if ((nChar >= 'a') && (nChar <= 'z')) {
            class_set(pNode->pClass, (unsigned char)(nChar - 'a' + 'A'));
        } else if ((nChar >= 'A') && (nChar <= 'Z')) {
            class_set(pNode->pClass, (unsigned char)(nChar - 'A' + 'a'));
        } else if ((nChar >= 0xC0) && (nChar <= 0xDE) && (nChar != 0xD7)) {
            class_set(pNode->pClass, (unsigned char)(nChar + 0x20));
        } else if ((nChar >= 0xE0) && (nChar <= 0xFE) && (nChar != 0xF7)) {
            class_set(pNode->pClass, (unsigned char)(nChar - 0x20));
        }
    }
}

static RENode *parse_class(REParser *pParser)
{
    RENode *pNode = node_alloc(pParser, RE_CLASS);
    if (!pNode) return NULL;

    pParser->p++; /* '[' */

    if ((pParser->p < pParser->pEnd) && (*pParser->p == '^')) {
        pNode->bNegateClass = 1;
        pParser->p++;
    }

    while ((pParser->p < pParser->pEnd) && (*pParser->p != ']')) {
        unsigned char nFrom = 0;

        if (*pParser->p == '\\') {
            unsigned char sTmp[32];
            int bHandled = 0;

            xx_rt_memset(sTmp, 0, sizeof(sTmp));
            pParser->p++;

            if (pParser->p >= pParser->pEnd) {
                parse_error(pParser, "\\ at end of pattern");

                break;
            }

            parse_escape_class(pParser, sTmp, &bHandled, 0);

            if (bHandled) {
                int i = 0;

                for (i = 0; i < 32; i++) {
                    pNode->pClass[i] |= sTmp[i];
                }

                continue;
            }

            if ((unsigned char)*pParser->p == 0xC2 || (unsigned char)*pParser->p == 0xC3) {
                int nWidth;
                nFrom = ((pParser->pRegExp->options & XX_REGEX_RAW_BYTES) ? (nWidth = 1, (unsigned char)*pParser->p) : latin1_at(pParser->p, (size_t)(pParser->pEnd - pParser->p), 0, &nWidth));
                pParser->p += nWidth;
            } else {
                nFrom = parse_escape_char(pParser);
            }
        } else {
            int nWidth;
            nFrom = ((pParser->pRegExp->options & XX_REGEX_RAW_BYTES) ? (nWidth = 1, (unsigned char)*pParser->p) : latin1_at(pParser->p, (size_t)(pParser->pEnd - pParser->p), 0, &nWidth));
            pParser->p += nWidth;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == '-') && ((pParser->p + 1) < pParser->pEnd) && (pParser->p[1] != ']')) {
            unsigned char nTo = 0;
            int i = 0;

            pParser->p++;

            if (*pParser->p == '\\') {
                pParser->p++;
                if (pParser->p < pParser->pEnd && ((unsigned char)*pParser->p == 0xC2 || (unsigned char)*pParser->p == 0xC3)) {
                    int nWidth;
                    nTo = ((pParser->pRegExp->options & XX_REGEX_RAW_BYTES) ? (nWidth = 1, (unsigned char)*pParser->p) : latin1_at(pParser->p, (size_t)(pParser->pEnd - pParser->p), 0, &nWidth));
                    pParser->p += nWidth;
                } else {
                    nTo = parse_escape_char(pParser);
                }
            } else {
                int nWidth;
                nTo = ((pParser->pRegExp->options & XX_REGEX_RAW_BYTES) ? (nWidth = 1, (unsigned char)*pParser->p) : latin1_at(pParser->p, (size_t)(pParser->pEnd - pParser->p), 0, &nWidth));
                pParser->p += nWidth;
            }

            if (nFrom > nTo) {
                parse_error(pParser, "range out of order in character class");

                break;
            }

            for (i = nFrom; i <= (int)nTo; i++) {
                class_set_ci(pParser, pNode, (unsigned char)i);
            }
        } else {
            class_set_ci(pParser, pNode, nFrom);
        }
    }

    if ((pParser->p < pParser->pEnd) && (*pParser->p == ']')) {
        pParser->p++;
    } else {
        parse_error(pParser, "missing terminating ] for character class");
    }

    if (pNode->bNegateClass) {
        class_invert(pNode->pClass);
    }

    return pNode;
}

static RENode *parse_atom(REParser *pParser)
{
    char nChar = 0;

    if (pParser->p >= pParser->pEnd) {
        return NULL;
    }

    nChar = *pParser->p;

    if (nChar == '(') {
        RENode *pNode = NULL;
        int bCapture = 1;
        int bLookahead = 0;
        int bNegate = 0;

        pParser->p++;

        if ((pParser->p < pParser->pEnd) && (pParser->p[0] == '?')) {
            char nKind = ((pParser->p + 1) < pParser->pEnd) ? pParser->p[1] : 0;

            if (nKind == ':') {
                bCapture = 0;
                pParser->p += 2;
            } else if (nKind == '=') {
                bCapture = 0;
                bLookahead = 1;
                pParser->p += 2;
            } else if (nKind == '!') {
                bCapture = 0;
                bLookahead = 1;
                bNegate = 1;
                pParser->p += 2;
            } else {
                /* The reference engine knows no named groups and no
                 * look-behind, so '(?<' is a syntax error there too.       */
                parse_error(pParser, "unrecognized character after (?");

                return NULL;
            }
        }

        pNode = node_alloc(pParser, bLookahead ? RE_LOOKAHEAD : RE_GROUP);
        if (!pNode) return NULL;
        pNode->bNegateLook = bNegate;
        pNode->nCaptureFirst = pParser->pRegExp->nGroups + 1;

        if (bCapture) {
            pParser->pRegExp->nGroups++;
            pNode->nGroupIndex = pParser->pRegExp->nGroups;
        }

        if (pParser->nDepth >= RE_MAX_PARSE_DEPTH) {
            parse_error(pParser, "regular expression is too deeply nested");

            return pNode;
        }

        pParser->nDepth++;
        pNode->pAlts = parse_alternatives(pParser, &pNode->nAltCount);
        pParser->nDepth--;
        pNode->nCaptureLast = pParser->pRegExp->nGroups;

        if ((pParser->p < pParser->pEnd) && (*pParser->p == ')')) {
            pParser->p++;
        } else {
            parse_error(pParser, "missing )");
        }

        return pNode;
    }

    if (nChar == '[') {
        return parse_class(pParser);
    }

    if (nChar == '.') {
        pParser->p++;

        return node_alloc(pParser, RE_ANY);
    }

    if (nChar == '^') {
        pParser->p++;

        return node_alloc(pParser, RE_BOL);
    }

    if (nChar == '$') {
        pParser->p++;

        return node_alloc(pParser, RE_EOL);
    }

    if (nChar == '\\') {
        unsigned char sTmp[32];
        int bHandled = 0;

        pParser->p++;

        if (pParser->p >= pParser->pEnd) {
            parse_error(pParser, "\\ at end of pattern");

            return NULL;
        }

        if (*pParser->p == 'b') {
            pParser->p++;

            return node_alloc(pParser, RE_WORDB);
        }

        if (*pParser->p == 'B') {
            pParser->p++;

            return node_alloc(pParser, RE_NWORDB);
        }

        if ((*pParser->p >= '1') && (*pParser->p <= '9')) {
            const char *pSave = pParser->p;
            uint64_t nValue = 0;

            while ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
                if (nValue <= RE_QUANT_LIMIT) {
                    nValue = nValue * 10 + (uint64_t)(*pParser->p - '0');
                }

                pParser->p++;
            }

            if (nValue <= (uint64_t)pParser->nGroupCount) {
                RENode *pNode = node_alloc(pParser, RE_BACKREF);
                if (!pNode) return NULL;

                pNode->nBackref = (int)nValue;

                return pNode;
            }

            /* Past the last group this is not a back reference: the reference
             * engine re-reads it as a legacy octal escape, and leaves '\8'
             * and '\9' as an atom that can never match.                     */
            pParser->p = pSave;

            if ((*pParser->p == '8') || (*pParser->p == '9')) {
                pParser->p++;

                return node_alloc(pParser, RE_CLASS); /* an empty class matches nothing */
            }

            {
                RENode *pNode = node_alloc(pParser, RE_CHAR);
                if (!pNode) return NULL;
                int nCode = 0;
                int nDigits = 0;

                while ((nDigits < 3) && (pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '7') && (((nCode * 8) + (*pParser->p - '0')) <= 255)) {
                    nCode = (nCode * 8) + (*pParser->p - '0');
                    pParser->p++;
                    nDigits++;
                }

                pNode->nChar = (unsigned char)nCode;

                return pNode;
            }
        }

        xx_rt_memset(sTmp, 0, sizeof(sTmp));
        parse_escape_class(pParser, sTmp, &bHandled, 1);

        if (bHandled) {
            RENode *pNode = node_alloc(pParser, RE_CLASS);
            if (!pNode) return NULL;

            xx_rt_memcpy(pNode->pClass, sTmp, sizeof(sTmp));

            return pNode;
        }

        {
            RENode *pNode = node_alloc(pParser, RE_CHAR);
            if (!pNode) return NULL;

            pNode->nChar = parse_escape_char(pParser);

            return pNode;
        }
    }

    {
        RENode *pNode = node_alloc(pParser, RE_CHAR);
        if (!pNode) return NULL;

        pNode->nChar = (unsigned char)nChar;
        pParser->p++;

        return pNode;
    }
}

static void parse_quantifier(REParser *pParser, RENode *pNode)
{
    if (pParser->p >= pParser->pEnd) {
        return;
    }

    if (*pParser->p == '*') {
        pNode->nMin = 0;
        pNode->nMax = -1;
        pParser->p++;
    } else if (*pParser->p == '+') {
        pNode->nMin = 1;
        pNode->nMax = -1;
        pParser->p++;
    } else if (*pParser->p == '?') {
        pNode->nMin = 0;
        pNode->nMax = 1;
        pParser->p++;
    } else if (*pParser->p == '{') {
        const char *pSave = pParser->p;
        uint64_t nMin = 0;
        uint64_t nMax = 0;
        int bBounded = 0;
        int bHasDigits = 0;
        int bTooBig = 0;

        pParser->p++;

        while ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
            nMin = nMin * 10 + (uint64_t)(*pParser->p - '0');

            if (nMin > RE_QUANT_LIMIT) {
                nMin = RE_QUANT_LIMIT;
                bTooBig = 1;
            }

            bHasDigits = 1;
            pParser->p++;
        }

        if (!bHasDigits) {
            pParser->p = pSave;

            return;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == ',')) {
            pParser->p++;

            if ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
                bBounded = 1;

                while ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
                    nMax = nMax * 10 + (uint64_t)(*pParser->p - '0');

                    if (nMax > RE_QUANT_LIMIT) {
                        nMax = RE_QUANT_LIMIT;
                        bTooBig = 1;
                    }

                    pParser->p++;
                }
            }
        } else {
            bBounded = 1;
            nMax = nMin;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == '}')) {
            pParser->p++;

            if (bBounded && (nMin > nMax)) {
                parse_error(pParser, "numbers out of order in {} quantifier");

                return;
            }

            if (bTooBig) {
                pNode->nMin = RE_QUANT_MAX;
                pNode->nMax = RE_QUANT_MAX;
            } else {
                pNode->nMin = (nMin > RE_QUANT_MAX) ? RE_QUANT_MAX : (int)nMin;
                pNode->nMax = bBounded ? ((nMax > RE_QUANT_MAX) ? RE_QUANT_MAX : (int)nMax) : -1;
            }
        } else {
            pParser->p = pSave;

            return;
        }
    } else {
        return;
    }

    if ((pParser->p < pParser->pEnd) && (*pParser->p == '?')) {
        pNode->bLazy = 1;
        pParser->p++;
    }
}

static RENode *parse_sequence(REParser *pParser)
{
    RENode *pFirst = NULL;
    RENode *pLast = NULL;

    while (pParser->p < pParser->pEnd) {
        RENode *pNode = NULL;

        if (pParser->bError) {
            break;
        }

        if ((*pParser->p == '|') || (*pParser->p == ')')) {
            break;
        }

        if (is_quantifier(pParser)) {
            parse_error(pParser, "nothing to repeat");

            break;
        }

        pNode = parse_atom(pParser);

        if (pNode == NULL) {
            break;
        }

        if (pLast) {
            pLast->pNext = pNode;
        } else {
            pFirst = pNode;
        }

        pLast = pNode;

        if (is_quantifier(pParser)) {
            if ((pNode->type == RE_BOL) || (pNode->type == RE_EOL) || (pNode->type == RE_WORDB) || (pNode->type == RE_NWORDB)) {
                parse_error(pParser, "nothing to repeat");

                break;
            }

            parse_quantifier(pParser, pNode);

            if (is_quantifier(pParser)) {
                parse_error(pParser, "nothing to repeat");

                break;
            }
        }
    }

    return pFirst;
}

static REAlt *parse_alternatives(REParser *pParser, int *pnCount)
{
    REAlt *pAlts = NULL;
    int nCount = 0;

    for (;;) {
        RENode *pSequence = parse_sequence(pParser);

        REAlt *pNew = (REAlt *)pParser->pAllocator->realloc_fn(pAlts, (size_t)(nCount + 1) * sizeof(REAlt));
        if (!pNew) {
            free_nodes(pSequence);
            parse_error(pParser, "out of memory");
            break;
        }
        pAlts = pNew;
        pAlts[nCount].pFirst = pSequence;
        nCount++;

        if (pParser->bError) {
            break;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == '|')) {
            pParser->p++;
            continue;
        }

        break;
    }

    *pnCount = nCount;

    return pAlts;
}

/* These are necessary conditions only. The matcher still decides whether a
 * candidate matches, preserving capture state and alternative ordering. */
static size_t re_size_add(size_t a, size_t b)
{
    return b > (size_t)-1 - a ? (size_t)-1 : a + b;
}

static size_t re_size_mul(size_t a, size_t b)
{
    return a && b > (size_t)-1 / a ? (size_t)-1 : a * b;
}

static void analyze_alts(REAlt *pAlts, int nCount, int bIgnoreCase)
{
    int i;

    for (i = 0; i < nCount; i++) {
        REAlt *pAlt = &pAlts[i];
        RENode *pNode;
        int bNeedFirst = 1;
        int bFixed = 1;
        size_t nFixedOffset = 0;

        pAlt->nMinBytes = 0;
        pAlt->nCheckOffset = 0;
        pAlt->nCheckChar = 0;
        pAlt->bCheckChar = 0;
        pAlt->bAnchored = pAlt->pFirst && pAlt->pFirst->type == RE_BOL;
        xx_rt_memset(pAlt->pFirstBytes, 0, sizeof(pAlt->pFirstBytes));

        for (pNode = pAlt->pFirst; pNode; pNode = pNode->pNext) {
            size_t nAtomMin = 0;
            unsigned char first[32];
            int j;

            xx_rt_memset(first, 0, sizeof(first));
            if (pNode->type == RE_GROUP) {
                analyze_alts(pNode->pAlts, pNode->nAltCount, bIgnoreCase);
                nAtomMin = (size_t)-1;
                for (j = 0; j < pNode->nAltCount; j++) {
                    REAlt *pChild = &pNode->pAlts[j];
                    int k;
                    if (pChild->nMinBytes < nAtomMin) nAtomMin = pChild->nMinBytes;
                    for (k = 0; k < 32; k++) first[k] |= pChild->pFirstBytes[k];
                }
            } else if (pNode->type == RE_LOOKAHEAD) {
                /* Assertions consume no bytes, but their nested groups use
                 * the same necessary-condition filters as ordinary groups. */
                analyze_alts(pNode->pAlts, pNode->nAltCount, bIgnoreCase);
            } else if (pNode->type == RE_CHAR) {
                nAtomMin = 1;
                class_set(first, pNode->nChar);
                if (bIgnoreCase) {
                    if (pNode->nChar >= 'a' && pNode->nChar <= 'z') class_set(first, (unsigned char)(pNode->nChar - 'a' + 'A'));
                    if (pNode->nChar >= 'A' && pNode->nChar <= 'Z') class_set(first, (unsigned char)(pNode->nChar - 'A' + 'a'));
                }
            } else if (pNode->type == RE_ANY || pNode->type == RE_CLASS) {
                int bC2 = 0, bC3 = 0;
                nAtomMin = 1;
                if (pNode->type == RE_ANY) {
                    xx_rt_memset(first, 0xFF, sizeof(first));
                    first['\n' >> 3] &= (unsigned char)~(1u << ('\n' & 7));
                    first['\r' >> 3] &= (unsigned char)~(1u << ('\r' & 7));
                } else {
                    xx_rt_memcpy(first, pNode->pClass, sizeof(first));
                    if (bIgnoreCase) {
                        for (j = 'a'; j <= 'z'; j++) {
                            unsigned char nUpper = (unsigned char)(j - 'a' + 'A');
                            if (class_get(first, (unsigned char)j) || class_get(first, nUpper)) {
                                class_set(first, (unsigned char)j);
                                class_set(first, nUpper);
                            }
                        }
                    }
                    for (j = 16; j < 24; j++) bC2 |= first[j];
                    for (j = 24; j < 32; j++) bC3 |= first[j];
                    /* A Latin-1 class atom can consume a UTF-8 pair. */
                    if (bC2) class_set(first, 0xC2);
                    if (bC3) class_set(first, 0xC3);
                }
            } else if (pNode->type == RE_BACKREF) {
                /* Unmatched captures are empty; participating captures can
                 * begin with any byte. No fixed-size assumption is safe. */
                xx_rt_memset(first, 0xFF, sizeof(first));
            }

            if (pNode->nMax != 0 && bNeedFirst) {
                for (j = 0; j < 32; j++) pAlt->pFirstBytes[j] |= first[j];
                if (nAtomMin && pNode->nMin) bNeedFirst = 0;
            }
            pAlt->nMinBytes = re_size_add(pAlt->nMinBytes, re_size_mul(nAtomMin, (size_t)pNode->nMin));

            if (bFixed) {
                if (pNode->type == RE_CHAR && pNode->nMin == pNode->nMax) {
                    if (pNode->nMin) {
                        nFixedOffset = re_size_add(nFixedOffset, (size_t)pNode->nMin);
                        pAlt->nCheckOffset = nFixedOffset - 1;
                        pAlt->nCheckChar = pNode->nChar;
                        pAlt->bCheckChar = 1;
                    }
                } else if (pNode->type == RE_CHAR || pNode->type == RE_ANY || pNode->type == RE_CLASS ||
                           pNode->type == RE_GROUP || pNode->type == RE_BACKREF) {
                    bFixed = 0;
                }
            }
        }
        pAlt->bCanEmpty = bNeedFirst;
    }
}

xx_regex *xx_regex_compile(const char *pPattern, const char *pFlags, char **ppError)
{
    return xx_regex_compile_n(pPattern, pPattern ? xx_rt_strlen(pPattern) : 0,
                              pFlags, pFlags ? xx_rt_strlen(pFlags) : 0, ppError);
}

xx_regex *xx_regex_compile_n(const char *pPattern, size_t nPatternSize, const char *pFlags,
                            size_t nFlagsSize, char **ppError)
{
    return xx_regex_compile_n_with_allocator(pPattern, nPatternSize, pFlags, nFlagsSize, ppError, NULL);
}

xx_regex *xx_regex_compile_with_allocator(const char *pPattern, const char *pFlags, char **ppError,
                                         const xx_regex_allocator *pAllocator)
{
    return xx_regex_compile_n_with_allocator(pPattern, pPattern ? xx_rt_strlen(pPattern) : 0,
                                             pFlags, pFlags ? xx_rt_strlen(pFlags) : 0, ppError, pAllocator);
}

static xx_regex *regex_compile_options(const char *pPattern, size_t nPatternSize, const char *pFlags,
    size_t nFlagsSize, char **ppError, const xx_regex_allocator *pAllocator, unsigned options);

xx_regex *xx_regex_compile_n_options(const char *pattern, size_t pattern_size,
    const char *flags, size_t flags_size, unsigned options, char **error)
{
    if (options & ~(XX_REGEX_RAW_BYTES | XX_REGEX_DOTALL)) return NULL;
    return regex_compile_options(pattern, pattern_size, flags, flags_size, error, NULL, options);
}

xx_regex *xx_regex_compile_n_with_allocator(const char *pPattern, size_t nPatternSize, const char *pFlags,
                                           size_t nFlagsSize, char **ppError, const xx_regex_allocator *pAllocator)
{
    return regex_compile_options(pPattern, nPatternSize, pFlags, nFlagsSize, ppError, pAllocator, 0);
}

static xx_regex *regex_compile_options(const char *pPattern, size_t nPatternSize, const char *pFlags,
    size_t nFlagsSize, char **ppError, const xx_regex_allocator *pAllocator, unsigned options)
{
    xx_regex *pRegExp;
    REParser parser;
    unsigned flags = 0;
    size_t i;

    if (ppError) *ppError = NULL;
    if (!pAllocator) pAllocator = &regex_default_allocator;
    if (!pAllocator->malloc_fn || !pAllocator->calloc_fn || !pAllocator->realloc_fn) return NULL;
    if ((!pPattern && nPatternSize) || (!pFlags && nFlagsSize) || nPatternSize > INT32_MAX) {
        if (ppError) *ppError = regex_strdup("invalid regular expression input", pAllocator);
        return NULL;
    }
    for (i = 0; i < nFlagsSize; ++i) {
        unsigned bit = pFlags[i] == 'g' ? 1u : pFlags[i] == 'i' ? 2u : pFlags[i] == 'm' ? 4u : 0u;
        if (!bit || (flags & bit)) {
            if (ppError) *ppError = regex_strdup(!bit ? "invalid regular expression flag" : "duplicate regular expression flag", pAllocator);
            return NULL;
        }
        flags |= bit;
    }
    pRegExp = (xx_regex *)pAllocator->calloc_fn(1, sizeof(xx_regex));
    if (!pRegExp) {
        if (ppError) *ppError = regex_strdup("out of memory", pAllocator);
        return NULL;
    }
    pRegExp->nRefs = 1;
    pRegExp->options = options;
    pRegExp->nSourceSize = nPatternSize;
    pRegExp->nFlagsSize = nFlagsSize;
    pRegExp->pSource = regex_strndup(pPattern, nPatternSize, pAllocator);
    pRegExp->pFlags = regex_strndup(pFlags, nFlagsSize, pAllocator);
    if (!pRegExp->pSource || !pRegExp->pFlags) {
        if (ppError) *ppError = regex_strdup("out of memory", pAllocator);
        xx_regex_free(pRegExp);
        return NULL;
    }
    pRegExp->bGlobal = (flags & 1u) != 0;
    pRegExp->bIgnoreCase = (flags & 2u) != 0;
    pRegExp->bMultiline = (flags & 4u) != 0;

    xx_rt_memset(&parser, 0, sizeof(parser));
    parser.p = pRegExp->pSource;
    parser.pEnd = pRegExp->pSource + pRegExp->nSourceSize;
    parser.pRegExp = pRegExp;
    parser.pAllocator = pAllocator;
    parser.bIgnoreCase = pRegExp->bIgnoreCase;
    parser.nGroupCount = count_groups(parser.p, parser.pEnd);

    pRegExp->pAlts = parse_alternatives(&parser, &pRegExp->nAltCount);

    if (parser.p < parser.pEnd) {
        /* parse_sequence stops at a ')' that closes nothing. */
        parse_error(&parser, "unmatched parentheses");
    }

    if (parser.bError) {
        if (ppError) {
            *ppError = parser.pError;
        } else {
            xx_rt_free(parser.pError);
        }

        xx_regex_free(pRegExp);

        return NULL;
    }

    analyze_alts(pRegExp->pAlts, pRegExp->nAltCount, pRegExp->bIgnoreCase);
    {
        int i;
        int bNullable = 0;
        int nFirstCount = 0;
        unsigned char first[32];
        xx_rt_memset(first, 0, sizeof(first));
        pRegExp->bAllAnchored = 1;
        pRegExp->nSingleFirstByte = -1;
        for (i = 0; i < pRegExp->nAltCount; i++) {
            int j;
            if (!pRegExp->pAlts[i].bAnchored) pRegExp->bAllAnchored = 0;
            bNullable |= pRegExp->pAlts[i].bCanEmpty;
            for (j = 0; j < 32; j++) first[j] |= pRegExp->pAlts[i].pFirstBytes[j];
        }
        if (!bNullable) {
            for (i = 0; i < 256; i++) {
                if (class_get(first, (unsigned char)i)) {
                    nFirstCount++;
                    pRegExp->nSingleFirstByte = i;
                }
            }
            /* ASCII cannot be a UTF-8 continuation byte, so memchr cannot
             * introduce starts skipped by the engine's Latin-1 stepping. */
            if (nFirstCount != 1 || pRegExp->nSingleFirstByte > 0x7F) pRegExp->nSingleFirstByte = -1;
        }
    }

    return pRegExp;
}

xx_regex *xx_regex_ref(xx_regex *pRegExp)
{
    if (pRegExp) xx_atomic_add(&pRegExp->nRefs, 1);
    return pRegExp;
}

static size_t re_alts_memory_size(REAlt *pAlts, int nCount)
{
    size_t nSize = re_size_mul((size_t)nCount, sizeof(REAlt));
    int i;
    for (i = 0; i < nCount; i++) {
        RENode *pNode;
        for (pNode = pAlts[i].pFirst; pNode; pNode = pNode->pNext) {
            nSize = re_size_add(nSize, sizeof(RENode));
            if (pNode->pAlts) nSize = re_size_add(nSize, re_alts_memory_size(pNode->pAlts, pNode->nAltCount));
        }
    }
    return nSize;
}

size_t xx_regex_memory_size(const xx_regex *pRegExp)
{
    size_t nSize;
    if (!pRegExp) return 0;
    nSize = re_size_add(sizeof(xx_regex), pRegExp->nSourceSize + 1);
    nSize = re_size_add(nSize, pRegExp->nFlagsSize + 1);
    return re_size_add(nSize, re_alts_memory_size(pRegExp->pAlts, pRegExp->nAltCount));
}

void xx_regex_free(xx_regex *pRegExp)
{
    if (pRegExp == NULL || xx_atomic_add(&pRegExp->nRefs, -1) != 1) {
        return;
    }

    free_alts(pRegExp->pAlts, pRegExp->nAltCount);
    xx_rt_free(pRegExp->pSource);
    xx_rt_free(pRegExp->pFlags);
    xx_rt_free(pRegExp);
}

int xx_regex_ngroups(const xx_regex *pRegExp)
{
    return pRegExp ? pRegExp->nGroups : 0;
}

int xx_regex_global(const xx_regex *pRegExp)
{
    return pRegExp ? pRegExp->bGlobal : 0;
}

int xx_regex_ignorecase(const xx_regex *pRegExp)
{
    return pRegExp ? pRegExp->bIgnoreCase : 0;
}

int xx_regex_multiline(const xx_regex *pRegExp)
{
    return pRegExp ? pRegExp->bMultiline : 0;
}

const char *xx_regex_source(const xx_regex *pRegExp)
{
    return pRegExp ? pRegExp->pSource : "";
}

const char *xx_regex_flags(const xx_regex *pRegExp)
{
    return pRegExp ? pRegExp->pFlags : "";
}

size_t xx_regex_source_size(const xx_regex *regex) { return regex ? regex->nSourceSize : 0; }
size_t xx_regex_flags_size(const xx_regex *regex) { return regex ? regex->nFlagsSize : 0; }

/* --------------------------------------------------------------- matcher  */

/* The step budget covers one whole xx_regex_exec call, not one start offset,
 * so that a pattern cannot cost nTextSize x the budget. Scanning n bytes for
 * a first match is legitimately O(n) steps, though, so the budget has to
 * grow with the subject or a plain literal search over a large string would
 * be cut short and report "no match". RE_STEPS_PER_BYTE leaves two orders of
 * magnitude of headroom over the few steps a start offset really costs,
 * while still turning the old unbounded nTextSize x RE_MAX_STEPS worst case
 * into a fixed ceiling.                                                     */
#define RE_MAX_STEPS 2000000
#define RE_STEPS_PER_BYTE 256
#define RE_MAX_STEPS_TOTAL 1500000000
/* Recursion guard: a quantified group recurses once per iteration, so a
 * pathological pattern over a long subject could otherwise exhaust the C
 * stack before the step counter fires.                                     */
#define RE_MAX_DEPTH 2000

typedef struct RECont RECont;

struct RECont {
    int nKind; /* 0 = match node chain, 1 = resume a repetition */
    RENode *pNode;
    RENode *pRepeat;
    int nDone;
    size_t nLastPos;
    RECont *pNext;
};

typedef struct {
    const char *pText;
    size_t nSize;
    const xx_regex *pRegExp;
    int32_t *pCaps;
    long nSteps;
    long nStepLimit; /* RE_MAX_STEPS scaled by the subject size, see above */
    int nDepth;
    /* An unbounded leading ASCII atom has no captures or assertions before
     * it. Once one start fails, later starts inside the same consumed run
     * can only try a subset of the suffix positions already tried. */
    RENode *pSearchPrefix;
    size_t nSearchStart;
    size_t nPrefixEnd;
    int bAborted; /* a budget fired: the result is 'do not know', not 'no match' */
    xx_regex_exec_status failure;
    xx_regex_abort_fn should_abort;
    void *abort_user;
    size_t poll_work;
    unsigned options;
    xx_regex_path_fn path;
    void *path_user;
} REMatcher;

static int matcher_abort(REMatcher *matcher, xx_regex_exec_status status)
{
    if (!matcher->bAborted) { matcher->bAborted = 1; matcher->failure = status; }
    return 0;
}

static int matcher_poll(REMatcher *matcher)
{
    if (matcher->bAborted) return 0;
    if (matcher->should_abort && matcher->should_abort(matcher->abort_user))
        return matcher_abort(matcher, XX_REGEX_EXEC_CANCELLED);
    matcher->poll_work = 0;
    return 1;
}

/* Charge helper loops too: failing necessary conditions must not provide an
 * unmetered route around the node/repetition budget or cancellation polling. */
static int matcher_work(REMatcher *matcher, size_t work)
{
    if (matcher->bAborted) return 0;
    if (matcher->nSteps > matcher->nStepLimit || work > (size_t)(matcher->nStepLimit - matcher->nSteps) + 1)
        return matcher_abort(matcher, XX_REGEX_EXEC_BUDGET);
    matcher->nSteps += (long)work;
    if (matcher->should_abort) {
        matcher->poll_work += work;
        if (matcher->poll_work >= 4096) return matcher_poll(matcher);
    }
    return 1;
}

#define RE_SCAN_CHUNK ((size_t)65536)
static const char *matcher_find_byte(REMatcher *matcher, size_t start, unsigned char byte)
{
    size_t size = matcher->nSize - start;
    if (!matcher->should_abort) return (const char *)xx_rt_memchr(matcher->pText + start, byte, size);
    while (size) {
        size_t chunk = size < RE_SCAN_CHUNK ? size : RE_SCAN_CHUNK;
        const char *found;
        if (!matcher_work(matcher, chunk)) return NULL;
        found = (const char *)xx_rt_memchr(matcher->pText + start, byte, chunk);
        if (found) return found;
        start += chunk; size -= chunk;
    }
    return NULL;
}

static const char *matcher_find_pair(REMatcher *matcher, size_t start, unsigned char first, size_t offset, unsigned char second)
{
    size_t size = matcher->nSize - start;
    if (!matcher->should_abort)
        return (const char *)xx_regex_find_pair((const unsigned char *)matcher->pText + start, size, first, offset, second);
    if (offset >= size) return NULL;
    size -= offset;
    while (size) {
        size_t chunk = size < RE_SCAN_CHUNK ? size : RE_SCAN_CHUNK;
        const char *found;
        if (!matcher_work(matcher, chunk)) return NULL;
        /* Include the check-byte overlap, but only chunk candidate starts.
         * The helper reads chunk bytes from each of its two spans. */
        found = (const char *)xx_regex_find_pair((const unsigned char *)matcher->pText + start, chunk + offset, first, offset, second);
        if (found) return found;
        start += chunk; size -= chunk;
    }
    return NULL;
}

static const char *matcher_find_newline(REMatcher *matcher, size_t start)
{
    size_t size = matcher->nSize - start;
    if (!matcher->should_abort) {
        const char *newline = (const char *)xx_rt_memchr(matcher->pText + start, '\n', size);
        const char *carriage = (const char *)xx_rt_memchr(matcher->pText + start, '\r', size);
        return !newline || (carriage && carriage < newline) ? carriage : newline;
    }
    while (size) {
        size_t chunk = size < RE_SCAN_CHUNK ? size : RE_SCAN_CHUNK;
        const char *newline, *carriage;
        if (!matcher_work(matcher, chunk)) return NULL;
        newline = (const char *)xx_rt_memchr(matcher->pText + start, '\n', chunk);
        carriage = (const char *)xx_rt_memchr(matcher->pText + start, '\r', chunk);
        if (!newline || (carriage && carriage < newline)) newline = carriage;
        if (newline) return newline;
        start += chunk; size -= chunk;
    }
    return NULL;
}

static int m_cont(REMatcher *pMatcher, RECont *pCont, size_t nPos, size_t *pnEnd);
static int m_node(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd);

static int m_after(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    RECont cont;

    cont.nKind = 0;
    cont.pNode = pNode->pNext;
    cont.pRepeat = NULL;
    cont.nDone = 0;
    cont.nLastPos = 0;
    cont.pNext = pCont;

    return m_cont(pMatcher, &cont, nPos, pnEnd);
}

static int single_match(REMatcher *pMatcher, RENode *pNode, size_t nPos)
{
    unsigned char nChar = 0;
    int nWidth = 1;

    if (!matcher_work(pMatcher, 1)) return 0;

    if (nPos >= pMatcher->nSize) {
        return 0;
    }

    nChar = (unsigned char)pMatcher->pText[nPos];

    switch (pNode->type) {
        case RE_CHAR:
            if (pMatcher->pRegExp->bIgnoreCase) {
                return (re_lower((char)nChar) == re_lower((char)pNode->nChar)) ? 1 : 0;
            }

            return (nChar == pNode->nChar) ? 1 : 0;

        case RE_ANY: nChar = (pMatcher->pRegExp->options & XX_REGEX_RAW_BYTES) ? (unsigned char)pMatcher->pText[nPos] : latin1_at(pMatcher->pText, pMatcher->nSize, nPos, &nWidth); return ((pMatcher->pRegExp->options & XX_REGEX_DOTALL) || (nChar != '\n' && nChar != '\r')) ? nWidth : 0;

        case RE_CLASS:
            nChar = (pMatcher->pRegExp->options & XX_REGEX_RAW_BYTES) ? (unsigned char)pMatcher->pText[nPos] : latin1_at(pMatcher->pText, pMatcher->nSize, nPos, &nWidth);
            if (class_get(pNode->pClass, nChar)) {
                return nWidth;
            }

            if (pMatcher->pRegExp->bIgnoreCase) {
                if ((nChar >= 'a') && (nChar <= 'z')) {
                    return class_get(pNode->pClass, (unsigned char)(nChar - 'a' + 'A')) ? nWidth : 0;
                }

                if ((nChar >= 'A') && (nChar <= 'Z')) {
                    return class_get(pNode->pClass, (unsigned char)(nChar - 'A' + 'a')) ? nWidth : 0;
                }
            }

            return 0;

        default: return 0;
    }
}

/* Compares nLength bytes of the subject at nPos with the captured text at
 * nStart. Both offsets are inside the subject by construction.             */
static int backref_at(REMatcher *pMatcher, size_t nStart, size_t nLength, size_t nPos)
{
    if (nPos > pMatcher->nSize || nLength > pMatcher->nSize - nPos) {
        return 0;
    }

    if (pMatcher->pRegExp->bIgnoreCase) {
        size_t i = 0;

        for (i = 0; i < nLength; i++) {
            if (!matcher_work(pMatcher, 1)) return 0;
            if (re_lower(pMatcher->pText[nPos + i]) != re_lower(pMatcher->pText[nStart + i])) {
                return 0;
            }
        }

        return 1;
    }

    /* Bound uncounted native comparison work, without charging the entire
     * capture for a mismatch near its beginning. */
    {
        size_t i = 0;
        while (i < nLength) {
            size_t nChunk = nLength - i;
            if (nChunk > 64) nChunk = 64;
            if (!matcher_work(pMatcher, nChunk)) return 0;
            if (xx_rt_memcmp(pMatcher->pText + nPos + i, pMatcher->pText + nStart + i, nChunk) != 0) return 0;
            i += nChunk;
        }
    }
    return 1;
}

static int is_word_at(REMatcher *pMatcher, size_t nPos)
{
    if (nPos >= pMatcher->nSize) {
        return 0;
    }

    return is_word_char((unsigned char)pMatcher->pText[nPos]);
}

static void record_search_prefix(REMatcher *pMatcher, RENode *pNode, size_t nPos, size_t nEnd)
{
    if (pNode == pMatcher->pSearchPrefix && nPos == pMatcher->nSearchStart) {
        pMatcher->nPrefixEnd = nEnd;
    }
}

/* Necessary conditions only. Kept shared between root and nested alternatives
 * so groups benefit from the immutable metadata without changing their order. */
static int alt_possible(REMatcher *pMatcher, REAlt *pAlt, size_t nPos)
{
    unsigned char nChar;
    if (!matcher_work(pMatcher, 1)) return 0;
    if (nPos > pMatcher->nSize || pAlt->nMinBytes > pMatcher->nSize - nPos) return 0;
    if (pAlt->bAnchored && pAlt->pFirst->nMin > 0 && nPos != 0 &&
        (!pMatcher->pRegExp->bMultiline || (pMatcher->pText[nPos - 1] != '\n' && pMatcher->pText[nPos - 1] != '\r'))) return 0;
    if (!pAlt->bCanEmpty && (nPos == pMatcher->nSize ||
        !class_get(pAlt->pFirstBytes, (unsigned char)pMatcher->pText[nPos]))) return 0;
    if (!pAlt->bCheckChar) return 1;
    if (pAlt->nCheckOffset >= pMatcher->nSize - nPos) return 0;
    nChar = (unsigned char)pMatcher->pText[nPos + pAlt->nCheckOffset];
    return pMatcher->pRegExp->bIgnoreCase ?
        re_lower((char)nChar) == re_lower((char)pAlt->nCheckChar) : nChar == pAlt->nCheckChar;
}

static int m_simple(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    size_t nMax = (pNode->nMax < 0) ? (size_t)-1 : (size_t)pNode->nMax;
    size_t nMin = (size_t)pNode->nMin;
    size_t nCount = 0;
    size_t nCurrent = nPos;
    int nWidth;

    if (pNode->bLazy) {
        for (nCount = 0; nCount < nMin; nCount++) {
            nWidth = single_match(pMatcher, pNode, nCurrent);
            if (!nWidth) {
                record_search_prefix(pMatcher, pNode, nPos, nCurrent);
                return 0;
            }
            nCurrent += (size_t)nWidth;
        }

        for (;;) {
            if (m_after(pMatcher, pNode, pCont, nCurrent, pnEnd)) {
                return 1;
            }

            if (nCount >= nMax) {
                record_search_prefix(pMatcher, pNode, nPos, nCurrent);
                return 0;
            }

            nWidth = single_match(pMatcher, pNode, nCurrent);
            if (!nWidth) {
                record_search_prefix(pMatcher, pNode, nPos, nCurrent);
                return 0;
            }

            nCurrent += (size_t)nWidth;
            nCount++;
        }
    }

    while ((nCount < nMax) && (nWidth = single_match(pMatcher, pNode, nCurrent))) {
        nCurrent += (size_t)nWidth;
        nCount++;
    }

    record_search_prefix(pMatcher, pNode, nPos, nCurrent);
    if (nCount < nMin) {
        return 0;
    }

    for (;;) {
        if (m_after(pMatcher, pNode, pCont, nCurrent, pnEnd)) {
            return 1;
        }

        if (nCount == nMin) {
            break;
        }

        if (!(pMatcher->pRegExp->options & XX_REGEX_RAW_BYTES) && (pNode->type == RE_CLASS || pNode->type == RE_ANY) && nCurrent >= nPos + 2 &&
            ((unsigned char)pMatcher->pText[nCurrent - 2] == 0xC2 || (unsigned char)pMatcher->pText[nCurrent - 2] == 0xC3) &&
            (unsigned char)pMatcher->pText[nCurrent - 1] >= 0x80 && (unsigned char)pMatcher->pText[nCurrent - 1] <= 0xBF) {
            nCurrent -= 2;
        } else {
            nCurrent--;
        }
        nCount--;
    }

    return 0;
}

/* Deterministic atom chains repeat without growing the C continuation stack.
 * Preserve exact boundaries for dot/class atoms, including mid-UTF-8 starts. */
static int simple_group_body(REMatcher *matcher, const RENode *group, size_t *width, int *fixed)
{
    const RENode *node;
    if (group->nAltCount != 1 || !group->pAlts[0].pFirst) return 0;
    *width = 0;
    *fixed = 1;
    for (node = group->pAlts[0].pFirst; node; node = node->pNext) {
        if (!matcher_work(matcher, 1)) return 0;
        if (node->nMin != 1 || node->nMax != 1 ||
            (node->type != RE_CHAR && node->type != RE_ANY && node->type != RE_CLASS)) return 0;
        ++*width;
        if (node->type != RE_CHAR) *fixed = 0;
    }
    return 1;
}

static int simple_group_iteration(REMatcher *matcher, RENode *group, size_t position, size_t *end)
{
    RENode *node;
    for (node = group->pAlts[0].pFirst; node; node = node->pNext) {
        int width = single_match(matcher, node, position);
        if (!width) return 0;
        position += (size_t)width;
    }
    *end = position;
    return 1;
}

static int m_simple_group(REMatcher *matcher, RENode *group, RECont *exit,
                           size_t position, size_t *end, size_t width, int fixed)
{
    size_t local_positions[64], *positions = local_positions, capacity = 64;
    size_t count = 0, current = position, maximum = group->nMax < 0 ? SIZE_MAX : (size_t)group->nMax;
    int32_t old_start = -1, old_end = -1;
    int result = 0;
    if (group->nGroupIndex > 0) {
        old_start = matcher->pCaps[group->nGroupIndex * 2];
        old_end = matcher->pCaps[group->nGroupIndex * 2 + 1];
    }
    positions[0] = position;
    for (;;) {
        size_t next;
        if (group->bLazy && count >= (size_t)group->nMin) {
            if (group->nGroupIndex > 0) {
                matcher->pCaps[group->nGroupIndex * 2] = count ? (int32_t)(fixed ? current - width : positions[count - 1]) : -1;
                matcher->pCaps[group->nGroupIndex * 2 + 1] = count ? (int32_t)current : -1;
            }
            if (m_cont(matcher, exit, current, end)) { result = 1; goto done; }
            if (matcher->bAborted) goto done;
        }
        if (count == maximum || !simple_group_iteration(matcher, group, current, &next)) break;
        if (!fixed && count + 1 == capacity) {
            size_t next_capacity;
            size_t *grown;
            if (capacity > SIZE_MAX / 2 / sizeof(size_t)) {
                matcher_abort(matcher, XX_REGEX_EXEC_NO_MEMORY);
                goto done;
            }
            next_capacity = capacity * 2;
            grown = positions == local_positions ?
                (size_t *)xx_rt_malloc(next_capacity * sizeof(size_t)) :
                (size_t *)xx_rt_realloc(positions, next_capacity * sizeof(size_t));
            if (!grown) { matcher_abort(matcher, XX_REGEX_EXEC_NO_MEMORY); goto done; }
            if (positions == local_positions) xx_rt_memcpy(grown, positions, capacity * sizeof(size_t));
            positions = grown;
            capacity = next_capacity;
        }
        current = next;
        ++count;
        if (!fixed) positions[count] = current;
    }
    if (matcher->bAborted || count < (size_t)group->nMin || group->bLazy) goto done;
    for (;;) {
        if (group->nGroupIndex > 0) {
            matcher->pCaps[group->nGroupIndex * 2] = count ? (int32_t)(fixed ? current - width : positions[count - 1]) : -1;
            matcher->pCaps[group->nGroupIndex * 2 + 1] = count ? (int32_t)current : -1;
        }
        if (m_cont(matcher, exit, current, end)) { result = 1; break; }
        if (matcher->bAborted || count == (size_t)group->nMin) break;
        --count;
        current = fixed ? current - width : positions[count];
    }
done:
    if (!result && group->nGroupIndex > 0) {
        matcher->pCaps[group->nGroupIndex * 2] = old_start;
        matcher->pCaps[group->nGroupIndex * 2 + 1] = old_end;
    }
    if (positions != local_positions) xx_rt_free(positions);
    return result;
}

/* Save the complete lexical capture range, including captures inside lookahead.
 * A repetition clears its range; a rejected branch restores the previous one. */
static int32_t *save_captures(REMatcher *matcher, const RENode *node, int32_t *local, size_t local_count, size_t *count)
{
    int32_t *saved;
    *count = node->nCaptureLast >= node->nCaptureFirst ?
        (size_t)(node->nCaptureLast - node->nCaptureFirst + 1) * 2 : 0;
    if (!*count) return local;
    saved = *count <= local_count ? local : (int32_t *)xx_rt_malloc(*count * sizeof(int32_t));
    if (!saved) { matcher_abort(matcher, XX_REGEX_EXEC_NO_MEMORY); return NULL; }
    xx_rt_memcpy(saved, matcher->pCaps + node->nCaptureFirst * 2, *count * sizeof(int32_t));
    return saved;
}

static void restore_captures(REMatcher *matcher, const RENode *node, const int32_t *saved, size_t count)
{
    if (count) xx_rt_memcpy(matcher->pCaps + node->nCaptureFirst * 2, saved, count * sizeof(int32_t));
}

static void clear_captures(REMatcher *matcher, const RENode *node, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (!matcher_work(matcher, 1)) return;
        matcher->pCaps[node->nCaptureFirst * 2 + i] = -1;
    }
}

static int m_group(REMatcher *pMatcher, RENode *pGroup, int nDone, RECont *pExit, size_t nPos, size_t *pnEnd)
{
    size_t nMax = (pGroup->nMax < 0) ? (size_t)-1 : (size_t)pGroup->nMax;
    size_t width, capture_count = 0;
    int32_t local_captures[32], *saved;
    int fixed, result = 0, i;
    if (!matcher_work(pMatcher, 1)) return 0;
    if (!nDone && simple_group_body(pMatcher, pGroup, &width, &fixed))
        return m_simple_group(pMatcher, pGroup, pExit, nPos, pnEnd, width, fixed);
    if (pGroup->bLazy && nDone >= pGroup->nMin && m_cont(pMatcher, pExit, nPos, pnEnd)) return 1;
    saved = save_captures(pMatcher, pGroup, local_captures, 32, &capture_count);
    if (!saved) return 0;
    if ((size_t)nDone < nMax) {
        RECont resume;
        resume.nKind = 1;
        resume.pNode = NULL;
        resume.pRepeat = pGroup;
        resume.nDone = nDone + 1;
        resume.nLastPos = nPos;
        resume.pNext = pExit;
        for (i = 0; i < pGroup->nAltCount; ++i) {
            if (!alt_possible(pMatcher, &pGroup->pAlts[i], nPos)) continue;
            clear_captures(pMatcher, pGroup, capture_count);
            if (!pGroup->pAlts[i].pFirst) {
                result = m_cont(pMatcher, &resume, nPos, pnEnd);
            } else {
                RECont body;
                body.nKind = 0;
                body.pNode = pGroup->pAlts[i].pFirst;
                body.pRepeat = NULL;
                body.nDone = 0;
                body.nLastPos = 0;
                body.pNext = &resume;
                result = m_cont(pMatcher, &body, nPos, pnEnd);
            }
            if (result) goto done;
            restore_captures(pMatcher, pGroup, saved, capture_count);
            if (pMatcher->bAborted) goto done;
        }
    }
    if (!pGroup->bLazy && nDone >= pGroup->nMin) result = m_cont(pMatcher, pExit, nPos, pnEnd);
done:
    if (!result) restore_captures(pMatcher, pGroup, saved, capture_count);
    if (saved != local_captures) xx_rt_free(saved);
    return result;
}

static int m_cont(REMatcher *pMatcher, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    int bResult = 0;

    if (!matcher_work(pMatcher, 1)) return 0;
    if (pMatcher->nDepth >= RE_MAX_DEPTH) return matcher_abort(pMatcher, XX_REGEX_EXEC_BUDGET);

    if (pMatcher->path) {
        int control = pMatcher->path(pMatcher->path_user, pMatcher->nSearchStart, nPos);
        if (control < 0) return matcher_abort(pMatcher, XX_REGEX_EXEC_CANCELLED);
        if (control > 0) return 0;
    }
    if (pCont == NULL) {
        if ((pMatcher->options & XX_REGEX_ENDANCHORED) && nPos != pMatcher->nSize) return 0;
        *pnEnd = nPos;

        return 1;
    }

    pMatcher->nDepth++;

    if (pCont->nKind == 0) {
        if (pCont->pNode == NULL) {
            bResult = m_cont(pMatcher, pCont->pNext, nPos, pnEnd);
        } else {
            bResult = m_node(pMatcher, pCont->pNode, pCont->pNext, nPos, pnEnd);
        }
    } else {
        RENode *pGroup = pCont->pRepeat;

        /* Once the minimum is satisfied, a further empty iteration must
         * fail before committing captures. Its caller can then backtrack to
         * a consuming alternative or leave the last real iteration intact. */
        if (nPos == pCont->nLastPos && pCont->nDone > pGroup->nMin) {
            bResult = 0;
        } else {
            if (pGroup->nGroupIndex > 0) {
                pMatcher->pCaps[pGroup->nGroupIndex * 2] = (int32_t)pCont->nLastPos;
                pMatcher->pCaps[pGroup->nGroupIndex * 2 + 1] = (int32_t)nPos;
            }
            bResult = m_group(pMatcher, pGroup, pCont->nDone, pCont->pNext, nPos, pnEnd);
        }
    }

    pMatcher->nDepth--;

    return bResult;
}

static int m_node(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    if (!matcher_work(pMatcher, 1)) return 0;

    switch (pNode->type) {
        case RE_BOL:
            if ((nPos == 0 && !(pMatcher->options & XX_REGEX_NOTBOL)) || (pMatcher->pRegExp->bMultiline && (pMatcher->pText[nPos - 1] == '\n' || pMatcher->pText[nPos - 1] == '\r'))) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;

        case RE_EOL:
            if ((nPos == pMatcher->nSize && !(pMatcher->options & XX_REGEX_NOTEOL)) || (pMatcher->pRegExp->bMultiline && (pMatcher->pText[nPos] == '\n' || pMatcher->pText[nPos] == '\r'))) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;

        case RE_WORDB: {
            int bBefore = (nPos > 0) ? is_word_at(pMatcher, nPos - 1) : 0;
            int bAfter = is_word_at(pMatcher, nPos);

            if (bBefore != bAfter) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;
        }

        case RE_NWORDB: {
            int bBefore = (nPos > 0) ? is_word_at(pMatcher, nPos - 1) : 0;
            int bAfter = is_word_at(pMatcher, nPos);

            if (bBefore == bAfter) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;
        }

        case RE_BACKREF: {
            int32_t nStart = 0;
            int32_t nEnd = 0;
            size_t nStride = 0;
            size_t nMax = 0;
            size_t nMin = 0;
            size_t nCount = 0;

            if ((pNode->nBackref < 1) || (pNode->nBackref > pMatcher->pRegExp->nGroups)) {
                return 0;
            }

            nStart = pMatcher->pCaps[pNode->nBackref * 2];
            nEnd = pMatcher->pCaps[pNode->nBackref * 2 + 1];

            if ((nStart < 0) || (nEnd < nStart)) {
                /* A group that did not take part in the match stands for the
                 * empty string, however often it is repeated.               */
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            nStride = (size_t)(nEnd - nStart);

            if (nStride == 0) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            nMax = (pNode->nMax < 0) ? (size_t)-1 : (size_t)pNode->nMax;
            nMin = (size_t)pNode->nMin;

            if (pNode->bLazy) {
                for (nCount = 0; nCount < nMin; nCount++) {
                    if (!backref_at(pMatcher, (size_t)nStart, nStride, nPos + nCount * nStride)) {
                        return 0;
                    }
                }

                for (;;) {
                    if (m_after(pMatcher, pNode, pCont, nPos + nCount * nStride, pnEnd)) {
                        return 1;
                    }

                    if (nCount >= nMax) {
                        return 0;
                    }

                    if (!backref_at(pMatcher, (size_t)nStart, nStride, nPos + nCount * nStride)) {
                        return 0;
                    }

                    nCount++;
                }
            }

            while ((nCount < nMax) && backref_at(pMatcher, (size_t)nStart, nStride, nPos + nCount * nStride)) {
                nCount++;
            }

            if (nCount < nMin) {
                return 0;
            }

            for (;;) {
                if (m_after(pMatcher, pNode, pCont, nPos + nCount * nStride, pnEnd)) {
                    return 1;
                }

                if (nCount == nMin) {
                    break;
                }

                nCount--;
            }

            return 0;
        }

        case RE_LOOKAHEAD: {
            size_t nDummy = 0, capture_count = 0;
            int32_t local_captures[32], *saved;
            int bMatched = 0, result = 0, i;
            if (pNode->nMin == 0) return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            saved = save_captures(pMatcher, pNode, local_captures, 32, &capture_count);
            if (!saved) return 0;
            for (i = 0; i < pNode->nAltCount; ++i) {
                RECont body;
                clear_captures(pMatcher, pNode, capture_count);
                if (!pNode->pAlts[i].pFirst) { bMatched = 1; break; }
                body.nKind = 0;
                body.pNode = pNode->pAlts[i].pFirst;
                body.pRepeat = NULL;
                body.nDone = 0;
                body.nLastPos = 0;
                body.pNext = NULL;
                if (m_cont(pMatcher, &body, nPos, &nDummy)) { bMatched = 1; break; }
                restore_captures(pMatcher, pNode, saved, capture_count);
                if (pMatcher->bAborted) break;
            }
            if (pNode->bNegateLook) {
                restore_captures(pMatcher, pNode, saved, capture_count);
                bMatched = !bMatched;
            }
            if (bMatched && !pMatcher->bAborted) result = m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            if (!result) restore_captures(pMatcher, pNode, saved, capture_count);
            if (saved != local_captures) xx_rt_free(saved);
            return result;
        }

        case RE_GROUP: {
            RECont exitCont;

            exitCont.nKind = 0;
            exitCont.pNode = pNode->pNext;
            exitCont.pRepeat = NULL;
            exitCont.nDone = 0;
            exitCont.nLastPos = 0;
            exitCont.pNext = pCont;

            return m_group(pMatcher, pNode, 0, &exitCont, nPos, pnEnd);
        }

        default: return m_simple(pMatcher, pNode, pCont, nPos, pnEnd);
    }
}

xx_regex_exec_status xx_regex_exec_ex(const xx_regex *regex, const char *text, size_t size, size_t start,
    int32_t *caps, xx_regex_abort_fn should_abort, void *abort_user)
{
    return xx_regex_exec_controlled(regex, text, size, start, caps, 0, should_abort, abort_user, NULL, NULL);
}

xx_regex_exec_status xx_regex_exec_controlled(const xx_regex *pRegExp, const char *pText, size_t nTextSize, size_t nStart,
    int32_t *pnCaps, unsigned options, xx_regex_abort_fn should_abort, void *abort_user,
    xx_regex_path_fn path, void *path_user)
{
    REMatcher matcher;
    size_t nPos = 0;

    if (pRegExp == NULL || !pText || !pnCaps || nTextSize > INT32_MAX || nStart > nTextSize) {
        return XX_REGEX_EXEC_INVALID;
    }

    matcher.pText = pText;
    matcher.nSize = nTextSize;
    matcher.pRegExp = pRegExp;
    matcher.pCaps = pnCaps;
    matcher.nSteps = 0;
    matcher.nDepth = 0;
    matcher.bAborted = 0;
    matcher.failure = XX_REGEX_EXEC_NO_MATCH;
    matcher.should_abort = should_abort;
    matcher.abort_user = abort_user;
    matcher.poll_work = 0;
    matcher.options = options;
    matcher.path = path;
    matcher.path_user = path_user;
    matcher.pSearchPrefix = NULL;
    if (pRegExp->nAltCount == 1 && pRegExp->pAlts[0].pFirst) {
        RENode *pFirst = pRegExp->pAlts[0].pFirst;
        if (pFirst->type == RE_CHAR && pFirst->nChar < 128 && pFirst->nMax < 0) {
            matcher.pSearchPrefix = pFirst;
        }
    }
    matcher.nSearchStart = matcher.nPrefixEnd = nStart;

    {
        uint64_t nLimit = (uint64_t)RE_MAX_STEPS + (uint64_t)nTextSize * (uint64_t)RE_STEPS_PER_BYTE;

        if (nLimit > (uint64_t)RE_MAX_STEPS_TOTAL) {
            nLimit = (uint64_t)RE_MAX_STEPS_TOTAL;
        }

        matcher.nStepLimit = (long)nLimit;
    }

    if (!matcher_poll(&matcher)) goto finished;

    for (nPos = nStart; nPos <= nTextSize; nPos++) {
        if ((options & XX_REGEX_ANCHORED) && nPos != nStart) break;
        int i = 0;

        if (pRegExp->bAllAnchored && nPos != 0) {
            const char *pNewline;
            if (!pRegExp->bMultiline) break;
            if (pText[nPos - 1] != '\n' && pText[nPos - 1] != '\r') {
                pNewline = matcher_find_newline(&matcher, nPos);
                if (!pNewline) break;
                nPos = (size_t)(pNewline - pText) + 1;
            }
        }
        if (!pRegExp->bAllAnchored && pRegExp->nSingleFirstByte >= 0) {
            const char *pFirst;
            if (!pRegExp->bIgnoreCase && pRegExp->nAltCount == 1 &&
                pRegExp->pAlts[0].bCheckChar && pRegExp->pAlts[0].nCheckOffset > 0 &&
                pRegExp->pAlts[0].nCheckChar < 128 && pRegExp->nSingleFirstByte < 128) {
                pFirst = matcher_find_pair(&matcher, nPos, (unsigned char)pRegExp->nSingleFirstByte,
                    pRegExp->pAlts[0].nCheckOffset, pRegExp->pAlts[0].nCheckChar);
            } else {
                pFirst = matcher_find_byte(&matcher, nPos, (unsigned char)pRegExp->nSingleFirstByte);
            }
            if (!pFirst) break;
            nPos = (size_t)(pFirst - pText);
        }

        matcher.nSearchStart = matcher.nPrefixEnd = nPos;
        for (i = 0; i <= pRegExp->nGroups; i++) {
            if (!matcher_work(&matcher, 1)) goto finished;
            pnCaps[i * 2] = -1;
            pnCaps[i * 2 + 1] = -1;
        }

        for (i = 0; i < pRegExp->nAltCount; i++) {
            REAlt *pAlt = &pRegExp->pAlts[i];
            size_t nEnd = 0;
            RECont body;

            /* An aborted earlier alternative cannot yield a reliable
             * later match, including the otherwise immediate empty branch. */
            if (matcher.bAborted) break;
            if (!alt_possible(&matcher, pAlt, nPos)) continue;
            {
                int group;
                for (group = 0; group <= pRegExp->nGroups; ++group) {
                    if (!matcher_work(&matcher, 1)) goto finished;
                    pnCaps[group * 2] = -1;
                    pnCaps[group * 2 + 1] = -1;
                }
            }

            if (pAlt->pFirst == NULL) {
                pnCaps[0] = (int32_t)nPos;
                pnCaps[1] = (int32_t)nPos;

                return XX_REGEX_EXEC_MATCH;
            }

            body.nKind = 0;
            body.pNode = pAlt->pFirst;
            body.pRepeat = NULL;
            body.nDone = 0;
            body.nLastPos = 0;
            body.pNext = NULL;

            if (m_cont(&matcher, &body, nPos, &nEnd)) {
                pnCaps[0] = (int32_t)nPos;
                pnCaps[1] = (int32_t)nEnd;

                return XX_REGEX_EXEC_MATCH;
            }
        }

        if (matcher.bAborted) {
            /* The budget covers the whole search: an attempt that was cut
             * short must not let a later offset pose as the first match.   */
            break;
        }
        if (matcher.nPrefixEnd > nPos) {
            /* The for-loop advances to the first untested character. ASCII
             * prefixes end at byte boundaries; the normal UTF handling below
             * still determines the next legal start after non-ASCII input. */
            nPos = matcher.nPrefixEnd - 1;
        }
        if (nPos < nTextSize) {
            int nWidth;
            if (pRegExp->options & XX_REGEX_RAW_BYTES) nWidth = 1;
            else latin1_at(pText, nTextSize, nPos, &nWidth);
            nPos += (size_t)nWidth - 1;
        }
    }

finished:
    for (int group = 0; group <= pRegExp->nGroups; ++group) {
        pnCaps[group * 2] = -1;
        pnCaps[group * 2 + 1] = -1;
    }
    return matcher.failure;
}

int xx_regex_exec(const xx_regex *regex, const char *text, size_t text_size, size_t start, int32_t *caps)
{
    return xx_regex_exec_ex(regex, text, text_size, start, caps, NULL, NULL) == XX_REGEX_EXEC_MATCH;
}

