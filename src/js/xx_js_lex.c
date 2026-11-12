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

/* js_lex.c - tokeniser. The whole source is converted into a token array up
 * front; regular-expression literals are disambiguated from division using
 * the classic "previous significant token" heuristic.                       */

#include "xx_js_lex.h"
#include "../regex/xx_regex_js_lex.h"

typedef struct {
    const char *pText;
    size_t nSize;
    JSTokType type;
} JSLexLiteral;

/* Literal lengths are fixed; calculating them for every source token adds
 * repeated scans to both of the lexer lookup loops. */
#define JS_LEX_LITERAL(text, token) {text, sizeof(text) - 1, token}

static const JSLexLiteral g_keywords[] = {JS_LEX_LITERAL("debugger", T_DEBUGGER),
                                         JS_LEX_LITERAL("break", T_BREAK),
                                         JS_LEX_LITERAL("case", T_CASE),
                                         JS_LEX_LITERAL("catch", T_CATCH),
                                         JS_LEX_LITERAL("continue", T_CONTINUE),
                                         JS_LEX_LITERAL("default", T_DEFAULT),
                                         JS_LEX_LITERAL("delete", T_DELETE),
                                         JS_LEX_LITERAL("do", T_DO),
                                         JS_LEX_LITERAL("else", T_ELSE),
                                         JS_LEX_LITERAL("finally", T_FINALLY),
                                         JS_LEX_LITERAL("for", T_FOR),
                                         JS_LEX_LITERAL("function", T_FUNCTION),
                                         JS_LEX_LITERAL("if", T_IF),
                                         JS_LEX_LITERAL("in", T_IN),
                                         JS_LEX_LITERAL("instanceof", T_INSTANCEOF),
                                         JS_LEX_LITERAL("new", T_NEW),
                                         JS_LEX_LITERAL("return", T_RETURN),
                                         JS_LEX_LITERAL("switch", T_SWITCH),
                                         JS_LEX_LITERAL("this", T_THIS),
                                         JS_LEX_LITERAL("throw", T_THROW),
                                         JS_LEX_LITERAL("try", T_TRY),
                                         JS_LEX_LITERAL("typeof", T_TYPEOF),
                                         JS_LEX_LITERAL("var", T_VAR),
                                         JS_LEX_LITERAL("void", T_VOID),
                                         JS_LEX_LITERAL("while", T_WHILE),
                                         JS_LEX_LITERAL("with", T_WITH),
                                         JS_LEX_LITERAL("null", T_NULL),
                                         JS_LEX_LITERAL("true", T_TRUE),
                                         JS_LEX_LITERAL("false", T_FALSE),
                                         JS_LEX_LITERAL("const", T_VAR),
                                         JS_LEX_LITERAL("let", T_VAR),
                                         {NULL, 0, T_EOF}};

static int is_ident_start(int nChar)
{
    return (((nChar >= 'a') && (nChar <= 'z')) || ((nChar >= 'A') && (nChar <= 'Z')) || (nChar == '_') || (nChar == '$') || (nChar >= 0x80)) ? 1 : 0;
}

static int is_ident_part(int nChar)
{
    return (is_ident_start(nChar) || ((nChar >= '0') && (nChar <= '9'))) ? 1 : 0;
}

static int is_digit(int nChar)
{
    return ((nChar >= '0') && (nChar <= '9')) ? 1 : 0;
}

static int is_hex(int nChar)
{
    return (is_digit(nChar) || ((nChar >= 'a') && (nChar <= 'f')) || ((nChar >= 'A') && (nChar <= 'F'))) ? 1 : 0;
}

static int hex_value(int nChar)
{
    if (is_digit(nChar)) {
        return nChar - '0';
    }

    if ((nChar >= 'a') && (nChar <= 'f')) {
        return nChar - 'a' + 10;
    }

    return nChar - 'A' + 10;
}

static void tok_push(JSLexer *pLexer, JSToken token)
{
    if (pLexer->nCount + 1 > pLexer->nCapacity) {
        size_t nNew = pLexer->nCapacity ? pLexer->nCapacity * 2 : 256;

        pLexer->pTokens = (JSToken *)xx_js_realloc(pLexer->pTokens, nNew * sizeof(JSToken));
        pLexer->nCapacity = nNew;
    }

    pLexer->pTokens[pLexer->nCount++] = token;
}

/* Appends a UTF-8 encoding of a code point (used for \uXXXX escapes). */
static void append_code_point(xx_buf_t *pBuf, unsigned int nCode)
{
    if (nCode < 0x80) {
        xx_buf_append_char(pBuf, (char)nCode);
    } else if (nCode < 0x800) {
        xx_buf_append_char(pBuf, (char)(0xC0 | (nCode >> 6)));
        xx_buf_append_char(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else if (nCode < 0x10000) {
        xx_buf_append_char(pBuf, (char)(0xE0 | (nCode >> 12)));
        xx_buf_append_char(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        xx_buf_append_char(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else {
        xx_buf_append_char(pBuf, (char)(0xF0 | (nCode >> 18)));
        xx_buf_append_char(pBuf, (char)(0x80 | ((nCode >> 12) & 0x3F)));
        xx_buf_append_char(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        xx_buf_append_char(pBuf, (char)(0x80 | (nCode & 0x3F)));
    }
}

/* Reports a lexical error in the same shape the parser uses. */
static void lex_error(char **ppError, const char *pMessage, int nLine)
{
    if (ppError) {
        char sBuf[128];

        xx_rt_snprintf(sBuf, sizeof(sBuf), "SyntaxError: %s at line %d", pMessage, nLine);
        *ppError = xx_js_strdup(sBuf);
    }
}

/* CRLF is one line terminator; standalone CR and LF also end a line. */
static const char *lex_after_line_end(const char *p)
{
    return p + ((*p == '\r' && p[1] == '\n') ? 2 : 1);
}

int js_lex_run(JSLexer *pLexer, const char *pSource, char **ppError)
{
    const char *p = pSource;
    const char *pLineStart = pSource;
    int nLine = 1;
    int bNewLineBefore = 0;

    xx_rt_memset(pLexer, 0, sizeof(*pLexer));
    pLexer->pSource = pSource;

    /* ECMAScript treats U+FEFF as whitespace; skipping it here keeps the
     * first identifier of a BOM-prefixed script from absorbing it.      */
    if (((unsigned char)p[0] == 0xEF) && ((unsigned char)p[1] == 0xBB) && ((unsigned char)p[2] == 0xBF)) {
        p += 3;
        pLineStart = p;
    }

    for (;;) {
        JSToken token;

        /* Skip whitespace and comments. */
        for (;;) {
            if (*p == 0) {
                break;
            }

            if (*p == '\n' || *p == '\r') {
                p = lex_after_line_end(p);
                nLine++;
                pLineStart = p;
                bNewLineBefore = 1;
            } else if ((*p == ' ') || (*p == '\t') || (*p == '\f') || (*p == '\v')) {
                p++;
            } else if ((p[0] == '/') && (p[1] == '/')) {
                while (*p && (*p != '\n') && (*p != '\r')) {
                    p++;
                }
            } else if ((p[0] == '/') && (p[1] == '*')) {
                p += 2;

                while (*p && !((p[0] == '*') && (p[1] == '/'))) {
                    if (*p == '\n' || *p == '\r') {
                        p = lex_after_line_end(p);
                        nLine++;
                        pLineStart = p;
                        bNewLineBefore = 1;
                    } else p++;
                }

                if (*p) {
                    p += 2;
                } else {
                    lex_error(ppError, "unterminated comment", nLine);

                    return 0;
                }
            } else {
                break;
            }
        }

        xx_rt_memset(&token, 0, sizeof(token));
        token.nLine = nLine;
        token.nColumn = (int)(p - pLineStart) + 1;
        token.bNewLineBefore = bNewLineBefore;
        bNewLineBefore = 0;

        if (*p == 0) {
            token.type = T_EOF;
            tok_push(pLexer, token);
            break;
        }

        /* Identifier or keyword. */
        if (is_ident_start((unsigned char)*p)) {
            const char *pStart = p;
            size_t nSize = 0;
            int i = 0;

            while (is_ident_part((unsigned char)*p)) {
                p++;
            }

            nSize = (size_t)(p - pStart);
            token.type = T_IDENT;
            for (i = 0; g_keywords[i].pText; i++) {
                if ((g_keywords[i].nSize == nSize) && (g_keywords[i].pText[0] == pStart[0]) &&
                    (xx_rt_memcmp(g_keywords[i].pText, pStart, nSize) == 0)) {
                    token.type = g_keywords[i].type;
                    token.pText = (char *)g_keywords[i].pText;
                    break;
                }
            }

            if (token.type == T_IDENT) token.pText = xx_js_strndup(pStart, nSize);
            tok_push(pLexer, token);
            continue;
        }

        /* Number. */
        if (is_digit((unsigned char)*p) || ((*p == '.') && is_digit((unsigned char)p[1]))) {
            const char *pStart = p;

            if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
                double nValue = 0;
                int nDigits = 0;

                p += 2;

                while (is_hex((unsigned char)*p)) {
                    nValue = nValue * 16 + hex_value((unsigned char)*p);
                    nDigits++;
                    p++;
                }

                if (nDigits == 0) {
                    lex_error(ppError, "invalid numeric literal", nLine);

                    return 0;
                }

                token.type = T_NUMBER;
                token.nNum = nValue;
                tok_push(pLexer, token);
                continue;
            }

            if ((p[0] == '0') && ((p[1] == 'b') || (p[1] == 'B'))) {
                double nValue = 0;
                int nDigits = 0;

                p += 2;

                while ((*p == '0') || (*p == '1')) {
                    nValue = nValue * 2 + (*p - '0');
                    nDigits++;
                    p++;
                }

                if (nDigits == 0) {
                    lex_error(ppError, "invalid numeric literal", nLine);

                    return 0;
                }

                token.type = T_NUMBER;
                token.nNum = nValue;
                tok_push(pLexer, token);
                continue;
            }

            if ((p[0] == '0') && ((p[1] == 'o') || (p[1] == 'O'))) {
                double nValue = 0;
                int nDigits = 0;

                p += 2;

                while ((*p >= '0') && (*p <= '7')) {
                    nValue = nValue * 8 + (*p - '0');
                    nDigits++;
                    p++;
                }

                if (nDigits == 0) {
                    lex_error(ppError, "invalid numeric literal", nLine);

                    return 0;
                }

                token.type = T_NUMBER;
                token.nNum = nValue;
                tok_push(pLexer, token);
                continue;
            }

            while (is_digit((unsigned char)*p)) {
                p++;
            }

            if (*p == '.') {
                p++;

                while (is_digit((unsigned char)*p)) {
                    p++;
                }
            }

            if ((*p == 'e') || (*p == 'E')) {
                const char *pSave = p;

                p++;

                if ((*p == '+') || (*p == '-')) {
                    p++;
                }

                if (is_digit((unsigned char)*p)) {
                    while (is_digit((unsigned char)*p)) {
                        p++;
                    }
                } else {
                    p = pSave;
                }
            }

            {
                size_t nLength = (size_t)(p - pStart);
                size_t i = 0;
                double nValue = 0;
                /* Every integer with at most 15 decimal digits is exact in
                 * binary64. Keep fractional / exponent / larger literals on
                 * strtod so its rounding and range behavior is preserved. */
                if (nLength <= 15) {
                    for (; i < nLength && is_digit((unsigned char)pStart[i]); i++) nValue = nValue * 10 + (pStart[i] - '0');
                }
                token.type = T_NUMBER;
                if (i == nLength) token.nNum = nValue;
                else {
                    char *pCopy = xx_js_strndup(pStart, nLength);
                    token.nNum = xx_rt_strtod(pCopy, NULL);
                    xx_js_free(pCopy);
                }
            }

            tok_push(pLexer, token);
            continue;
        }

        /* String literal. */
        if ((*p == '"') || (*p == '\'')) {
            char nQuote = *p;
            int nStartLine = nLine;
            xx_buf_t buf;

            xx_buf_init(&buf);
            p++;

            while (*p && (*p != nQuote)) {
                if (*p == '\\') {
                    p++;

                    switch (*p) {
                        case 'n':
                            xx_buf_append_char(&buf, '\n');
                            p++;
                            break;
                        case 't':
                            xx_buf_append_char(&buf, '\t');
                            p++;
                            break;
                        case 'r':
                            xx_buf_append_char(&buf, '\r');
                            p++;
                            break;
                        case 'b':
                            xx_buf_append_char(&buf, '\b');
                            p++;
                            break;
                        case 'f':
                            xx_buf_append_char(&buf, '\f');
                            p++;
                            break;
                        case 'v':
                            xx_buf_append_char(&buf, '\v');
                            p++;
                            break;
                        case '0':
                            if (!is_digit((unsigned char)p[1])) {
                                xx_buf_append_char(&buf, '\0');
                                p++;
                            } else {
                                xx_buf_append_char(&buf, *p);
                                p++;
                            }
                            break;
                        case 'x':
                            if (is_hex((unsigned char)p[1]) && is_hex((unsigned char)p[2])) {
                                xx_buf_append_char(&buf, (char)((hex_value((unsigned char)p[1]) << 4) | hex_value((unsigned char)p[2])));
                                p += 3;
                            } else {
                                xx_buf_append_char(&buf, 'x');
                                p++;
                            }
                            break;
                        case 'u':
                            if (is_hex((unsigned char)p[1]) && is_hex((unsigned char)p[2]) && is_hex((unsigned char)p[3]) && is_hex((unsigned char)p[4])) {
                                unsigned int nCode = (unsigned int)((hex_value((unsigned char)p[1]) << 12) | (hex_value((unsigned char)p[2]) << 8) |
                                                                    (hex_value((unsigned char)p[3]) << 4) | hex_value((unsigned char)p[4]));

                                /* A high surrogate followed by \uDC00-\uDFFF is one code
                                 * point; encoding the halves apart would give CESU-8.   */
                                if ((nCode >= 0xD800) && (nCode <= 0xDBFF) && (p[5] == '\\') && (p[6] == 'u') && is_hex((unsigned char)p[7]) &&
                                    is_hex((unsigned char)p[8]) && is_hex((unsigned char)p[9]) && is_hex((unsigned char)p[10])) {
                                    unsigned int nLow = (unsigned int)((hex_value((unsigned char)p[7]) << 12) | (hex_value((unsigned char)p[8]) << 8) |
                                                                       (hex_value((unsigned char)p[9]) << 4) | hex_value((unsigned char)p[10]));

                                    if ((nLow >= 0xDC00) && (nLow <= 0xDFFF)) {
                                        nCode = 0x10000 + ((nCode - 0xD800) << 10) + (nLow - 0xDC00);
                                        p += 6;
                                    }
                                }

                                append_code_point(&buf, nCode);
                                p += 5;
                            } else {
                                xx_buf_append_char(&buf, 'u');
                                p++;
                            }
                            break;
                        case '\n':
                        case '\r':
                            p = lex_after_line_end(p);
                            nLine++;
                            pLineStart = p;
                            break;
                        case 0: break;
                        default:
                            xx_buf_append_char(&buf, *p);
                            p++;
                            break;
                    }
                } else {
                    if (*p == '\n' || *p == '\r') {
                        const char *pEnd = lex_after_line_end(p);
                        /* Preserve the existing accepted raw-string bytes. */
                        xx_buf_append(&buf, p, (size_t)(pEnd - p));
                        p = pEnd;
                        nLine++;
                        pLineStart = p;
                    } else {
                        xx_buf_append_char(&buf, *p);
                        p++;
                    }
                }
            }

            if (*p == nQuote) {
                p++;
            } else {
                xx_buf_free(&buf);
                lex_error(ppError, "unterminated string literal", nStartLine);

                return 0;
            }

            token.type = T_STRING;
            token.nTextSize = buf.size;
            token.pText = xx_buf_detach(&buf, NULL);
            if (!token.pText) {
                /* The buffer's own allocation failed: no token without text. */
                lex_error(ppError, "out of memory", nStartLine);

                return 0;
            }
            tok_push(pLexer, token);
            continue;
        }

        /* Regular expression literal. */
        {
            const char *pEnd;

            if (js_regex_lex_literal(pLexer, p, &token, &pEnd)) {
                tok_push(pLexer, token);
                p = pEnd;
                continue;
            }
        }

        /* Punctuators, longest match first. There is deliberately no '**' or
         * '**=': QJSEngine rejects exponentiation, so lexing them as two
         * tokens keeps the SyntaxError the reference reports.               */
        {
            static const JSLexLiteral punctuators[] = {JS_LEX_LITERAL(">>>=", T_USHR_ASSIGN),
                                                       JS_LEX_LITERAL("===", T_SEQ),
                                                       JS_LEX_LITERAL("!==", T_SNE),
                                                       JS_LEX_LITERAL("<<=", T_SHL_ASSIGN),
                                                       JS_LEX_LITERAL(">>=", T_SHR_ASSIGN),
                                                       JS_LEX_LITERAL(">>>", T_USHR),
                                                       JS_LEX_LITERAL("...", T_ELLIPSIS),
                                                       JS_LEX_LITERAL("==", T_EQ),
                                                       JS_LEX_LITERAL("!=", T_NE),
                                                       JS_LEX_LITERAL("<=", T_LE),
                                                       JS_LEX_LITERAL(">=", T_GE),
                                                       JS_LEX_LITERAL("&&", T_LAND),
                                                       JS_LEX_LITERAL("||", T_LOR),
                                                       JS_LEX_LITERAL("++", T_INC),
                                                       JS_LEX_LITERAL("--", T_DEC),
                                                       JS_LEX_LITERAL("<<", T_SHL),
                                                       JS_LEX_LITERAL(">>", T_SHR),
                                                       JS_LEX_LITERAL("+=", T_ADD_ASSIGN),
                                                       JS_LEX_LITERAL("-=", T_SUB_ASSIGN),
                                                       JS_LEX_LITERAL("*=", T_MUL_ASSIGN),
                                                       JS_LEX_LITERAL("/=", T_DIV_ASSIGN),
                                                       JS_LEX_LITERAL("%=", T_MOD_ASSIGN),
                                                       JS_LEX_LITERAL("&=", T_AND_ASSIGN),
                                                       JS_LEX_LITERAL("|=", T_OR_ASSIGN),
                                                       JS_LEX_LITERAL("^=", T_XOR_ASSIGN),
                                                       JS_LEX_LITERAL("=>", T_ARROW),
                                                       JS_LEX_LITERAL("{", T_LBRACE),
                                                       JS_LEX_LITERAL("}", T_RBRACE),
                                                       JS_LEX_LITERAL("(", T_LPAREN),
                                                       JS_LEX_LITERAL(")", T_RPAREN),
                                                       JS_LEX_LITERAL("[", T_LBRACKET),
                                                       JS_LEX_LITERAL("]", T_RBRACKET),
                                                       JS_LEX_LITERAL(";", T_SEMI),
                                                       JS_LEX_LITERAL(",", T_COMMA),
                                                       JS_LEX_LITERAL("<", T_LT),
                                                       JS_LEX_LITERAL(">", T_GT),
                                                       JS_LEX_LITERAL("+", T_ADD),
                                                       JS_LEX_LITERAL("-", T_SUB),
                                                       JS_LEX_LITERAL("*", T_MUL),
                                                       JS_LEX_LITERAL("/", T_DIV),
                                                       JS_LEX_LITERAL("%", T_MOD),
                                                       JS_LEX_LITERAL("&", T_AND),
                                                       JS_LEX_LITERAL("|", T_OR),
                                                       JS_LEX_LITERAL("^", T_XOR),
                                                       JS_LEX_LITERAL("!", T_NOT),
                                                       JS_LEX_LITERAL("~", T_BNOT),
                                                       JS_LEX_LITERAL("?", T_QUESTION),
                                                       JS_LEX_LITERAL(":", T_COLON),
                                                       JS_LEX_LITERAL("=", T_ASSIGN),
                                                       JS_LEX_LITERAL(".", T_DOT),
                                                       {NULL, 0, T_EOF}};
#undef JS_LEX_LITERAL
            int i = 0;
            int bFound = 0;

            for (i = 0; punctuators[i].pText; i++) {
                size_t nSize = punctuators[i].nSize;

                if ((p[0] == punctuators[i].pText[0]) && (xx_rt_strncmp(p, punctuators[i].pText, nSize) == 0)) {
                    token.type = punctuators[i].type;
                    tok_push(pLexer, token);
                    p += nSize;
                    bFound = 1;
                    break;
                }
            }

            if (bFound) {
                continue;
            }
        }

        if (ppError) {
            char sBuf[128];

            xx_rt_snprintf(sBuf, sizeof(sBuf), "SyntaxError: unexpected character '%c' at line %d", *p, nLine);
            *ppError = xx_js_strdup(sBuf);
        }

        return 0;
    }

    return 1;
}

void js_lex_free(JSLexer *pLexer)
{
    size_t i = 0;

    for (i = 0; i < pLexer->nCount; i++) {
        /* Keyword text points at the immutable literal table. */
        JSTokType type = pLexer->pTokens[i].type;
        if (type == T_IDENT || type == T_STRING || type == T_REGEXP) xx_js_free(pLexer->pTokens[i].pText);
        xx_js_free(pLexer->pTokens[i].pText2);
    }

    xx_js_free(pLexer->pTokens);
    xx_rt_memset(pLexer, 0, sizeof(*pLexer));
}
