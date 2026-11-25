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

/* JavaScript regex literal scanning and slash/division disambiguation. */

#include "xx_regex_js_lex.h"

/* Match the general JavaScript lexer's identifier-part rule exactly. Flags
 * are accepted lexically here; regex compilation decides their semantics. */
static int regexp_flag_ident_part(int nChar)
{
    return (((nChar >= 'a') && (nChar <= 'z')) || ((nChar >= 'A') && (nChar <= 'Z')) || ((nChar >= '0') && (nChar <= '9')) || (nChar == '_') || (nChar == '$') ||
            (nChar >= 0x80))
               ? 1
               : 0;
}

static size_t matching_open(const JSLexer *pLexer, size_t nClose, JSTokType open, JSTokType close)
{
    size_t depth = 0, i = nClose + 1;
    while (i > 0) {
        JSTokType type = pLexer->pTokens[--i].type;
        if (type == close) depth++;
        else if (type == open && --depth == 0) return i;
    }
    return SIZE_MAX;
}

static int control_paren(const JSLexer *pLexer, size_t nClose)
{
    size_t nOpen = matching_open(pLexer, nClose, T_LPAREN, T_RPAREN);
    if (nOpen == SIZE_MAX || nOpen == 0) return 0;
    /* Keywords are valid property names: obj.if() closes a call expression,
     * so a following slash remains division rather than a statement regex. */
    if (nOpen > 1 && pLexer->pTokens[nOpen - 2].type == T_DOT) return 0;
    switch (pLexer->pTokens[nOpen - 1].type) {
        case T_IF:
        case T_WHILE:
        case T_FOR:
        case T_WITH:
        case T_SWITCH:
        case T_CATCH: return 1;
        default: return 0;
    }
}

/* A closed statement block can be followed by a regex expression statement;
 * closed object and function expressions must still allow ordinary division. */
static int statement_block(const JSLexer *pLexer, size_t nClose)
{
    size_t nOpen = matching_open(pLexer, nClose, T_LBRACE, T_RBRACE);
    JSTokType previous;
    if (nOpen == SIZE_MAX) return 0;
    if (nOpen == 0) return 1;
    previous = pLexer->pTokens[nOpen - 1].type;
    switch (previous) {
        case T_SEMI:
        case T_LBRACE:
        case T_RBRACE:
        case T_ELSE:
        case T_TRY:
        case T_FINALLY:
        case T_DO: return 1;
        case T_RPAREN: {
            size_t nParams, nFunction;
            if (control_paren(pLexer, nOpen - 1)) return 1;
            nParams = matching_open(pLexer, nOpen - 1, T_LPAREN, T_RPAREN);
            if (nParams == SIZE_MAX || nParams == 0) return 0;
            nFunction = nParams - 1;
            if (pLexer->pTokens[nFunction].type == T_IDENT && nFunction > 0) nFunction--;
            if (pLexer->pTokens[nFunction].type != T_FUNCTION) return 0;
            if (nFunction == 0) return 1;
            switch (pLexer->pTokens[nFunction - 1].type) {
                case T_SEMI:
                case T_LBRACE:
                case T_RBRACE:
                case T_ELSE: return 1;
                default: return 0;
            }
        }
        default: return 0;
    }
}

static int prev_allows_regexp(const JSLexer *pLexer)
{
    const JSToken *pPrev;
    if (pLexer->nCount == 0) return 1;
    pPrev = &pLexer->pTokens[pLexer->nCount - 1];
    switch (pPrev->type) {
        case T_RPAREN: return control_paren(pLexer, pLexer->nCount - 1);
        case T_RBRACE: return statement_block(pLexer, pLexer->nCount - 1);
        case T_IDENT:
        case T_NUMBER:
        case T_STRING:
        case T_REGEXP:
        case T_RBRACKET:
        case T_THIS:
        case T_TRUE:
        case T_FALSE:
        case T_NULL:
        case T_INC:
        case T_DEC: return 0;
        default: return 1;
    }
}

int js_regex_lex_literal(const JSLexer *pLexer, const char *pInput, JSToken *pToken, const char **ppEnd)
{
    if ((*pInput == '/') && prev_allows_regexp(pLexer)) {
        const char *pStart = pInput + 1;
        const char *q = pStart;
        int bInClass = 0;

        while (*q) {
            if (*q == '\\') {
                /* A regex literal cannot cross a line terminator, even
                 * with a preceding backslash. Leave normal lexing to
                 * account for the newline on this invalid input. */
                if (q[1] == 0 || q[1] == '\n' || q[1] == '\r') {
                    break;
                }

                q += 2;
                continue;
            }

            if (*q == '[') {
                bInClass = 1;
            } else if (*q == ']') {
                bInClass = 0;
            } else if ((*q == '/') && (!bInClass)) {
                break;
            } else if (*q == '\n' || *q == '\r') {
                break;
            }

            q++;
        }

        if (*q == '/') {
            const char *pFlags = q + 1;
            const char *pFlagsEnd = pFlags;

            while (regexp_flag_ident_part((unsigned char)*pFlagsEnd)) {
                pFlagsEnd++;
            }

            pToken->type = T_REGEXP;
            pToken->pText = xx_js_strndup(pStart, (size_t)(q - pStart));
            pToken->pText2 = xx_js_strndup(pFlags, (size_t)(pFlagsEnd - pFlags));
            *ppEnd = pFlagsEnd;
            return 1;
        }
    }

    return 0;
}
