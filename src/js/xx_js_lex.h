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

#ifndef JS_LEX_H
#define JS_LEX_H

#include "xx_js_deps.h"

typedef enum {
    T_EOF = 0,
    T_IDENT,
    T_NUMBER,
    T_STRING,
    T_REGEXP,

    /* keywords */
    T_BREAK, T_CASE, T_CATCH, T_CONTINUE, T_DEFAULT, T_DELETE, T_DO, T_ELSE,
    T_FINALLY, T_FOR, T_FUNCTION, T_IF, T_IN, T_INSTANCEOF, T_NEW, T_RETURN,
    T_SWITCH, T_THIS, T_THROW, T_TRY, T_TYPEOF, T_VAR, T_VOID, T_WHILE, T_WITH,
    T_NULL, T_TRUE, T_FALSE,

    /* punctuators */
    T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_SEMI,
    T_COMMA, T_DOT, T_COLON, T_QUESTION, T_ARROW, T_ELLIPSIS,
    T_ASSIGN, T_ADD_ASSIGN, T_SUB_ASSIGN, T_MUL_ASSIGN, T_DIV_ASSIGN,
    T_MOD_ASSIGN, T_AND_ASSIGN, T_OR_ASSIGN, T_XOR_ASSIGN, T_SHL_ASSIGN,
    T_SHR_ASSIGN, T_USHR_ASSIGN,
    T_EQ, T_NE, T_SEQ, T_SNE, T_LT, T_GT, T_LE, T_GE,
    T_ADD, T_SUB, T_MUL, T_DIV, T_MOD,
    T_AND, T_OR, T_XOR, T_NOT, T_BNOT,
    T_LAND, T_LOR, T_INC, T_DEC, T_SHL, T_SHR, T_USHR
} JSTokType;

typedef struct {
    JSTokType type;
    int nLine;
    int bNewLineBefore;
    double nNum;
    char *pText;
    char *pText2; /* regexp flags */
    size_t nTextSize;
} JSToken;

typedef struct {
    const char *pSource;
    JSToken *pTokens;
    size_t nCount;
    size_t nCapacity;
} JSLexer;

int js_lex_run(JSLexer *pLexer, const char *pSource, char **ppError);
void js_lex_free(JSLexer *pLexer);

#endif /* JS_LEX_H */
